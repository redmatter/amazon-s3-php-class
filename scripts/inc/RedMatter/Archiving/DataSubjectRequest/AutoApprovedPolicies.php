<?php
/**
 * AutoApprovedPolicies.php
 *
 * PHP version 5
 *
 * @category  AutoApprovedPolicies.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */


namespace RedMatter\Archiving\DataSubjectRequest;


class AutoApprovedPolicies
{
    // list of auto-approval enabled policy-ids
    // [p1, p2]
    private $_autoApprovePolicies;

    // an array of array with user-id as key and array policy-ids he/she is a custodian of
    // [ u1 => [p1, p2], ... ]
    private $_policyCustodians;

    public function __construct($autoApprovePolicies, $policyCustodians)
    {
        $this->_autoApprovePolicies = $autoApprovePolicies;
        $this->_policyCustodians = $policyCustodians;
    }

    public function getPoliciesByUserId($userId)
    {
        return isset($this->_policyCustodians[$userId]) ?
            array_unique(array_merge($this->_autoApprovePolicies, $this->_policyCustodians[$userId])) :
            $this->_autoApprovePolicies;
    }

    public static function Create($orgId)
    {
        $autoApprovePolicies = [];
        $policyCustodians = [];

        /** @var \XPath $xpath */
        $xpath = \API::Get("/archiving/policy/{$orgId}/all");
        /** @var \DOMNodeList $policyNodes */
        $policyNodes = $xpath('/Archiving/Policies/Policy', true);
        foreach ($policyNodes as $policyNode)
        {
            $policyId = (int)$xpath('ID', false, $policyNode);

            if ('yes' === $xpath('AutoApproveDataSubjectRequest', false, $policyNode))
            {
                $autoApprovePolicies[] = $policyId;
            }

            $custodians = $xpath('DataCustodians/DataCustodian/@UserID', true, $policyNode);
            if ($custodians !== null)
            {
                $policyCustodians[$policyId] = [];
                /** @var \DOMNodeList $custodians */
                foreach ($custodians as $userId)
                {
                    $userId = (int)$userId;

                    if (!isset($policyCustodians[$userId]))
                    {
                        $policyCustodians[$userId] = [];
                    }

                    $policyCustodians[$userId][] = $policyId;
                }
            }
        }

        return new self($autoApprovePolicies, $policyCustodians);
    }
}
